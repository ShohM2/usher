#include "process_each_node.hpp"
#include "src/matOptimize/Profitable_Moves_Enumerators/Profitable_Moves_Enumerators.hpp"
#include "src/matOptimize/mutation_annotated_tree.hpp"

//Mutation added by moving src to LCA doesn't match any sensitive mutation on LCA. Spliting the edge is equivalent to adding a binary node with parent state and src state at this node
static void added_no_match(const Mutation_Count_Change &mutation_to_add,
                           Mutation_Count_Change_Collection &out,
                           int &parsimony_score_change,
                           MAT::Node *src_branch_node) {
    //assert(mutation_to_add.get_par_state() ==get_parent_state(src_branch_node, mutation_to_add.get_position()));
    nuc_one_hot major_allele =
        mutation_to_add.get_par_state() & mutation_to_add.get_incremented();
    if (!major_allele) {
        parsimony_score_change++;
        major_allele =
            mutation_to_add.get_par_state() | mutation_to_add.get_incremented();
        out.emplace_back(mutation_to_add);
        out.back().set_ori_state(mutation_to_add.get_par_state());
        out.back().set_change(0, mutation_to_add.get_incremented(),
                              major_allele);
    }
}

/**
 * @brief Calculate parsimony score change and fitch set change if the src node is moved directly under LCA (dst==LCA)
        LCA
        /
     src_branch_node
      /
    ...
    /
 src
 * @param src_branch_node the immediate child of LCA that is on path from src to LCA
 * @param dst_mutations the mutation needed to perserve state of src if it is moved under the LCA (as sibling of src_branch_node)
 * @param src_branch_node_mutations_altered change in fitch set of src_branch_node just from removal of src
 * @param out the change in fitch set of the node formed from spliting the branch between LCA and src_branch_node to the original state of src_branch_node
 * @param parsimony_score_change
 * @return whether there are mutations that src_branch_node have but the src node have, if not it is better to split between src_branch_node and its descendent, rather than here.
 */
bool LCA_place_mezzanine(
    MAT::Node *src_branch_node,
    const Mutation_Count_Change_Collection &dst_mutations,
    const Mutation_Count_Change_Collection &src_branch_node_mutations_altered,
    Mutation_Count_Change_Collection &out, int &parsimony_score_change
) {
    //TODO: use the common three way merge template
    bool have_not_shared=false;

    auto src_branch_node_change_iter = src_branch_node_mutations_altered.begin();
    auto src_branch_node_change_end = src_branch_node_mutations_altered.end();

    auto added_iter = dst_mutations.begin();
    auto added_end = dst_mutations.end();

    for (const MAT::Mutation &m : src_branch_node->mutations) {
        //Mutation unique in the moved src, even though it is not shared,
        // it is not because the mutation at src_branch_node, so still the same
        //parsimony score as inserting here or its descendant, so cannot be the sole reason of trying here.
        while (added_iter < added_end &&
                added_iter->get_position() < m.get_position()) {
            added_no_match(*added_iter, out, parsimony_score_change, src_branch_node);
            //assert(added_iter < added_end );
            added_iter++;
        }

        nuc_one_hot major_allele = m.get_all_major_allele();
        nuc_one_hot new_major_allele = major_allele;
        int score_change = 0;
        //Override the Fitch set of src_branch_node if modified
        if (src_branch_node_change_iter != src_branch_node_change_end &&
                src_branch_node_change_iter->get_position() == m.get_position()) {
            major_allele = src_branch_node_change_iter->get_new_state();
            src_branch_node_change_iter++;
        }
        //Coincide
        if (added_iter < added_end &&
                m.get_position() == added_iter->get_position()) {
            if (added_iter->get_incremented()&major_allele) {
                new_major_allele=added_iter->get_incremented()&major_allele;
            } else {
                //This is a newly added mutation while traversing to LCA
                have_not_shared=true;
                new_major_allele=added_iter->get_incremented()|major_allele;
                score_change++;
            }
            //assert(added_iter < added_end );
            added_iter++;
        } else {
            //The newly added node follow LCA like the src_branch_node
            have_not_shared=true;
            score_change+=get_new_major_allele_binary_node(major_allele, m.get_par_one_hot(), new_major_allele);
        }
        register_change_from_new_state(out, 0, m, new_major_allele);
        parsimony_score_change += score_change;
    }
    while (added_iter<added_end) {
        added_no_match(*added_iter, out, parsimony_score_change, src_branch_node);
        added_iter++;
    }
    return have_not_shared;
}
void check_parsimony_score_change_above_LCA(MAT::Node *LCA, int &parsimony_score_change,
        Mutation_Count_Change_Collection &parent_added,
        const std::vector<MAT::Node *> &node_stack_from_src,
        std::vector<MAT::Node *> &node_stack_above_LCA,
        Mutation_Count_Change_Collection &parent_of_parent_added,
        MAT::Node *ancestor) {
    while (ancestor && (!parent_added.empty())) {
        parent_of_parent_added.clear();
        get_intermediate_nodes_mutations(
            ancestor,
            node_stack_above_LCA.empty() ? node_stack_from_src.back()
            : node_stack_above_LCA.back(),
            parent_added, parent_of_parent_added, parsimony_score_change);
        node_stack_above_LCA.push_back(ancestor);
        parent_added.swap(parent_of_parent_added);
        ancestor = ancestor->parent;
    }
    // Hit root
    if (!ancestor) {
        for (auto &a : parent_added) {
            parsimony_score_change += a.get_default_change_internal();
        }
        ancestor = node_stack_above_LCA.back();
    }
    if (node_stack_above_LCA.empty()) {
        node_stack_above_LCA.push_back(LCA);
    }
}
/**
 * @brief Calculate parsimony score change due to state changes of nodes above LCA
 * @param src
 * @param dst
 * @param LCA
 * @param mutations mutations needed above src node for moving src under parent of dst
 * @param root_mutations_altered fitch set change of src_branch_node from removing src
 * @param parsimony_score_change
 * @param node_stack_from_dst nodes from dst to LCA
 * @param parent_added state change of dst_branch_node from inserting src to its new location
 * @param node_stack_from_src nodes from src to LCA
 * @param node_stack_above_LCA node above LCA that have change in their Fitch set
 * @param debug_above_LCA
 * @return The highest node had their Fitch set changed
 */